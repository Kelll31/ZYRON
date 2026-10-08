"""Self-tests for rt_lint.py. Run: python -m unittest discover -s .claude/hooks -p "test_*.py" """
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import rt_lint

HERE = Path(__file__).resolve().parent


def rules(text: str, entry_names: bool = True) -> set[str]:
    return {f.rule_id for f in rt_lint.scan_source(text, "t.cpp", entry_names)}


class ScanSourceTests(unittest.TestCase):
    def test_clean_process_block_has_no_findings(self):
        src = "void Deck::processBlock(float* out, int n) {\n  for (int i = 0; i < n; ++i) out[i] *= gain;\n}\n"
        self.assertEqual(rt_lint.scan_source(src), [])

    def test_allocation_in_process_block_is_an_error(self):
        src = "void Deck::processBlock(float* out, int n) {\n  auto* tmp = new float[n];\n}\n"
        found = rt_lint.scan_source(src)
        self.assertEqual([(f.rule_id, f.severity, f.line) for f in found], [("alloc", "error", 2)])

    def test_mutex_and_logging_are_errors(self):
        src = ("void Mixer::process(int n) {\n  std::lock_guard<std::mutex> g(m);\n"
               "  DBG(\"x\");\n}\n")
        self.assertTrue({"lock", "io"} <= rules(src))

    def test_banned_words_in_comments_and_strings_are_ignored(self):
        src = ('void A::processBlock(int n) {\n  // new float[n]; std::mutex\n'
               '  const char* s = "throw new std::mutex";\n  /* malloc(3) */\n}\n')
        self.assertEqual(rt_lint.scan_source(src), [])

    def test_allocation_in_prepare_is_not_flagged(self):
        src = "void A::prepareToPlay(int n) {\n  buf = new float[n];\n  v.resize(n);\n}\n"
        self.assertEqual(rt_lint.scan_source(src), [])

    def test_rt_marker_makes_any_function_realtime(self):
        src = "// RT\nstatic void mixChannel(float* p, int n)\n{\n  std::vector<float> tmp(n);\n}\n"
        self.assertIn("container", rules(src, entry_names=False))

    def test_unmarked_helper_is_not_scanned(self):
        src = "static void helper(int n) {\n  auto* p = new int[n];\n}\n"
        self.assertEqual(rt_lint.scan_source(src), [])

    def test_marker_on_declaration_is_ignored(self):
        src = "// RT\nvoid mixChannel(float* p, int n);\nvoid other() { auto* p = new int; }\n"
        self.assertEqual(rt_lint.scan_source(src, entry_names=False), [])

    def test_rt_prose_comment_is_not_a_marker(self):
        src = "// RT-safe helper, allocates lazily\nvoid f(int n) { auto* p = new int[n]; }\n"
        self.assertEqual(rt_lint.scan_source(src, entry_names=False), [])

    def test_call_site_is_not_mistaken_for_definition(self):
        src = "void run() {\n  engine.process(buf);\n  process(buf);\n  auto* p = new int;\n}\n"
        self.assertEqual(rt_lint.scan_source(src), [])

    def test_placement_new_is_allowed(self):
        src = "void A::process(int n) {\n  new (storage) Voice();\n}\n"
        self.assertNotIn("alloc", rules(src))

    def test_vector_push_back_is_a_warning_not_an_error(self):
        src = "void A::process(int n) {\n  events.push_back(e);\n}\n"
        found = rt_lint.scan_source(src)
        self.assertEqual([(f.rule_id, f.severity) for f in found], [("growth", "warn")])

    def test_function_local_static_is_a_warning(self):
        src = "void A::process(int n) {\n  static Table table = makeTable();\n}\n"
        self.assertIn("static-local", rules(src))

    def test_static_cast_and_constexpr_are_fine(self):
        src = "void A::process(int n) {\n  static constexpr int k = 4;\n  auto x = static_cast<float>(n);\n}\n"
        self.assertEqual(rt_lint.scan_source(src), [])

    def test_qualifiers_between_signature_and_body_are_handled(self):
        src = "void A::processBlock(float* o, int n) noexcept override {\n  throw 1;\n}\n"
        self.assertIn("throw", rules(src))

    def test_line_numbers_are_one_based_and_exact(self):
        src = "\n\nvoid A::process(int n) {\n  int ok = 1;\n  std::thread t;\n}\n"
        self.assertEqual([f.line for f in rt_lint.scan_source(src)], [5])

    def test_unterminated_function_does_not_crash(self):
        self.assertIsInstance(rt_lint.scan_source("void A::process(int n) {\n  new int;\n"), list)


class PathAndHookTests(unittest.TestCase):
    def test_classify_path_scopes_entry_names_to_audio_dirs(self):
        self.assertEqual(rt_lint.classify_path("E:\\p\\src\\Audio\\Mixer\\Mixer.cpp"), (True, True))
        self.assertEqual(rt_lint.classify_path("/p/src/Recording/Writer.h"), (True, True))
        self.assertEqual(rt_lint.classify_path("/p/src/Analysis/Bpm.cpp"), (True, False))
        self.assertEqual(rt_lint.classify_path("/p/tests/AudioTest.cpp"), (False, False))
        self.assertEqual(rt_lint.classify_path("/p/src/Audio/notes.md"), (False, False))

    def test_hook_reports_findings_with_exit_code_2(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Path(tmp) / "src" / "Audio" / "Deck.cpp"
            f.parent.mkdir(parents=True)
            f.write_text("void Deck::processBlock(int n) {\n  auto* p = new int[n];\n}\n", encoding="utf-8")
            code, message = rt_lint.run_hook({"tool_input": {"file_path": str(f)}})
        self.assertEqual(code, 2)
        self.assertIn("[error] alloc", message)

    def test_hook_ignores_other_files_missing_files_and_bad_payloads(self):
        self.assertEqual(rt_lint.run_hook({}), (0, ""))
        self.assertEqual(rt_lint.run_hook({"tool_input": {"file_path": "/nope/src/Audio/x.cpp"}}), (0, ""))
        self.assertEqual(rt_lint.run_hook({"tool_input": {"file_path": 5}}), (0, ""))

    def test_cli_hook_mode_end_to_end(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Path(tmp) / "src" / "Audio" / "Bad.cpp"
            f.parent.mkdir(parents=True)
            f.write_text("void X::process(int n) {\n  std::mutex m;\n}\n", encoding="utf-8")
            proc = subprocess.run(
                [sys.executable, str(HERE / "rt_lint.py")],
                input=json.dumps({"tool_name": "Write", "tool_input": {"file_path": str(f)}}),
                capture_output=True, text=True, timeout=30,
            )
        self.assertEqual(proc.returncode, 2)
        self.assertIn("lock", proc.stderr)

    def test_cli_hook_mode_survives_garbage_stdin(self):
        proc = subprocess.run([sys.executable, str(HERE / "rt_lint.py")], input="not json",
                              capture_output=True, text=True, timeout=30)
        self.assertEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
