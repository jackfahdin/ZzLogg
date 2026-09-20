import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
from release_notes import release_changes


class ChangelogTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / 'CHANGELOG.md'

    def write(self, text):
        self.path.write_text(text, encoding='utf-8')

    def test_stable_uses_exact_version_only(self):
        self.write('# 更新日志\n## [Unreleased]\n- future\n'
                   '## [26.09.01] - 2026-09-20\n### 新增\n- 高亮\n'
                   '## [26.09.00] - 2026-09-19\n- old\n')
        notes = release_changes(self.path, '26.09.01', 'stable')
        self.assertIn('高亮', notes)
        self.assertNotIn('future', notes)
        self.assertNotIn('old', notes)

    def test_preview_combines_unreleased_and_current_version(self):
        self.write('## [Unreleased]\n- future\n## [26.09.01]\n- current\n')
        notes = release_changes(self.path, '26.09.01', 'nightly')
        self.assertLess(notes.index('future'), notes.index('current'))

    def test_preview_can_use_unreleased_before_version_is_cut(self):
        self.write('## [Unreleased]\n- future\n')
        self.assertIn('future', release_changes(self.path, '26.09.02', 'nightly'))
        with self.assertRaises(ValueError):
            release_changes(self.path, '26.09.02', 'stable')

    def test_missing_empty_or_duplicate_entry_is_rejected(self):
        for text in ('# No version\n', '## [26.09.01]\n\n',
                     '## [26.09.01]\n- first\n## [26.09.01]\n- duplicate\n'):
            with self.subTest(text=text):
                self.write(text)
                with self.assertRaises(ValueError):
                    release_changes(self.path, '26.09.01', 'stable')

    def test_fenced_headings_are_not_version_boundaries(self):
        self.write('## [26.09.01]\nExample:\n```markdown\n## [26.09.00]\n```\n- current\n'
                   '## [26.08.00]\n- old\n')
        notes = release_changes(self.path, '26.09.01', 'stable')
        self.assertIn('## [26.09.00]', notes)
        self.assertIn('current', notes)
        self.assertNotIn('- old', notes)


if __name__ == '__main__':
    unittest.main()
