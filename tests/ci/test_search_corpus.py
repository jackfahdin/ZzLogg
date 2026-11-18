"""The external-log runner must use fixed, disposable copies of real files."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('search_corpus', ROOT / 'scripts/dev/search_corpus.py')
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)


class SearchCorpusTests(unittest.TestCase):
    def make_manifest(self, root, cases=None):
        path = root / 'queries.json'
        path.write_text(json.dumps({'version': 1, 'cases': cases or [
            {'id': 'errors', 'file': 'log with spaces.txt', 'encoding': 'UTF-8',
             'pattern': 'ERROR', 'kind': 'literal'}]}), encoding='utf-8')
        return path

    def test_snapshot_is_fixed_and_source_is_not_replaced_or_deleted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'log with spaces.txt'
            contents = b'ERROR\n' + bytes(range(256)) * 8192  # Cross the streaming buffer boundary.
            source.write_bytes(contents)
            manifest = self.make_manifest(root)
            with corpus.prepare_corpus(manifest) as prepared:
                case = json.loads(prepared['manifest'].read_text(encoding='utf-8'))['cases'][0]
                snapshot = Path(case['file'])
                self.assertNotEqual(snapshot, source)
                self.assertTrue(snapshot.is_file())
                self.assertEqual(snapshot.read_bytes(), contents)
                self.assertEqual(prepared['metadata']['files'][0]['sha256'], hashlib.sha256(contents).hexdigest())
                source.write_bytes(b'changed after snapshot')
                self.assertEqual(snapshot.read_bytes(), contents)
            self.assertFalse(snapshot.exists())
            self.assertEqual(source.read_bytes(), b'changed after snapshot')
            self.assertTrue(manifest.exists())

    def test_same_file_is_copied_once_for_multiple_queries(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'log with spaces.txt'
            source.write_bytes(b'ERROR\n')
            cases = [{'id': name, 'file': str(source) if i else source.name,
                      'encoding': 'UTF-8', 'pattern': name, 'kind': 'literal'}
                     for i, name in enumerate(('ERROR', 'WARN'))]
            with corpus.prepare_corpus(self.make_manifest(root, cases)) as prepared:
                copied = json.loads(prepared['manifest'].read_text())['cases']
                self.assertEqual(copied[0]['file'], copied[1]['file'])
                self.assertEqual(len(prepared['metadata']['files']), 1)

    def test_missing_files_and_directories_fail_before_measurement(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.make_manifest(root)
            with self.assertRaises(ValueError):
                with corpus.prepare_corpus(manifest):
                    self.fail('Missing file accepted')
            (root / 'log with spaces.txt').mkdir()
            with self.assertRaises(ValueError):
                with corpus.prepare_corpus(manifest):
                    self.fail('Directory accepted')

    def test_growth_during_copy_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'log with spaces.txt'
            source.write_bytes(b'ERROR\n')
            manifest = self.make_manifest(root)
            original_open = Path.open
            class GrowingReader:
                def __init__(self, reader):
                    self.reader = reader
                    self.changed = False
                def __enter__(self):
                    return self
                def __exit__(self, *args):
                    self.reader.close()
                def fileno(self):
                    return self.reader.fileno()
                def read(self, size):
                    block = self.reader.read(size)
                    if not self.changed:
                        self.changed = True
                        with original_open(source, 'ab') as writer:
                            writer.write(b'new log entry\n')
                    return block
            def open_with_growth(path, *args, **kwargs):
                stream = original_open(path, *args, **kwargs)
                return GrowingReader(stream) if path == source and args == ('rb',) else stream
            with patch.object(Path, 'open', open_with_growth):
                with self.assertRaisesRegex(ValueError, 'changed while copying'):
                    with corpus.prepare_corpus(manifest):
                        self.fail('Growing log accepted')
            self.assertTrue(source.read_bytes().endswith(b'new log entry\n'))

    def test_output_cannot_contain_an_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.make_manifest(root)
            with self.assertRaisesRegex(ValueError, 'must not contain'):
                corpus.validate_output_path(manifest, root)
            corpus.validate_output_path(manifest, root / 'separate-results')

    def test_output_replaces_links_without_truncating_the_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source.log'
            source.write_bytes(b'original log contents')
            for kind in ('hardlink', 'symlink'):
                with self.subTest(kind=kind):
                    source.write_bytes(b'original log contents')
                    output = root / (kind + '.jsonl')
                    try:
                        os.link(source, output) if kind == 'hardlink' else output.symlink_to(source)
                    except OSError as error:
                        self.skipTest(f'File links unavailable: {error}')
                    corpus.atomic_write_text(output, 'new report')
                    self.assertEqual(source.read_bytes(), b'original log contents')
                    self.assertEqual(output.read_text(), 'new report')
                    self.assertFalse(output.is_symlink())

    def test_no_manifest_preserves_generated_mode(self):
        with corpus.prepare_corpus(None) as prepared:
            self.assertIsNone(prepared)


if __name__ == '__main__':
    unittest.main()
