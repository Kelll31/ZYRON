"""Tests for check_notices.py. Run: python -m unittest discover -s scripts -p "test_*.py" """
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import check_notices

CMAKE = """
macro(zyron_require_juce)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3)
endmacro()
FetchContent_Declare( Catch2 GIT_REPOSITORY x GIT_TAG y )
# FetchContent_Declare(CommentedOut)
"""


class DeclaredDependenciesTests(unittest.TestCase):
    def test_finds_every_fetchcontent_declaration(self):
        self.assertEqual(check_notices.declared_dependencies(CMAKE), ["JUCE", "Catch2"])

    def test_ignores_commented_out_declarations(self):
        self.assertNotIn("CommentedOut", check_notices.declared_dependencies(CMAKE))

    def test_empty_text_has_no_dependencies(self):
        self.assertEqual(check_notices.declared_dependencies(""), [])


class MissingNoticesTests(unittest.TestCase):
    def test_all_covered_returns_nothing(self):
        notices = "| JUCE | 9.0.3 |\n| catch2 | v3 |"
        self.assertEqual(check_notices.missing_notices(["JUCE", "Catch2"], notices), [])

    def test_matching_is_case_insensitive_and_whole_word(self):
        self.assertEqual(check_notices.missing_notices(["JUCE"], "| juce | x |"), [])
        self.assertEqual(check_notices.missing_notices(["ONNX"], "onnxruntime is planned"), ["ONNX"])

    def test_reports_each_missing_dependency_once_in_order(self):
        self.assertEqual(check_notices.missing_notices(["A", "B", "A"], "nothing"), ["A", "B"])


class MainTests(unittest.TestCase):
    def _repo(self, tmp: str, cmake: str, notices: str) -> Path:
        root = Path(tmp)
        (root / "cmake").mkdir()
        (root / "cmake" / "ZyronDependencies.cmake").write_text(cmake, encoding="utf-8")
        (root / "THIRD_PARTY_NOTICES.md").write_text(notices, encoding="utf-8")
        return root

    def test_exit_zero_when_everything_is_listed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self._repo(tmp, CMAKE, "JUCE and Catch2")
            self.assertEqual(check_notices.main(root), 0)

    def test_exit_one_when_a_dependency_is_unlisted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self._repo(tmp, CMAKE, "JUCE only")
            self.assertEqual(check_notices.main(root), 1)

    def test_exit_two_when_files_are_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(check_notices.main(Path(tmp)), 2)


if __name__ == "__main__":
    unittest.main()
