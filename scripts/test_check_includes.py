"""Tests for check_includes.py. Run: python -m unittest discover -s scripts -p "test_*.py" """
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import check_includes

REPO = Path(__file__).resolve().parent.parent


def violations_for(files: dict[str, str]) -> list[check_includes.Violation]:
    """Writes `files` (path under src/ -> text) into a temp repo and lints it."""
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for relative, text in files.items():
            path = root / "src" / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        return check_includes.check(root)


def rules(files: dict[str, str]) -> set[str]:
    return {v.rule for v in violations_for(files)}


class ModuleDirectionTests(unittest.TestCase):
    def test_a_module_may_include_itself_and_core(self):
        self.assertEqual(rules({"Audio/DSP/A.cpp": '#include "Audio/DSP/B.hpp"\n#include "Core/Commands/Command.hpp"\n'}), set())

    def test_core_includes_nothing_else_of_the_project(self):
        self.assertEqual(rules({"Core/State/A.hpp": '#include "Audio/DSP/OutputStage.hpp"\n'}), {"module-direction"})

    def test_ui_must_not_include_audio_internals(self):
        self.assertEqual(rules({"UI/Panel.cpp": '#include "Audio/Engine/AudioEngine.hpp"\n'}), {"module-direction"})

    def test_audio_must_not_include_ui(self):
        self.assertEqual(rules({"Audio/Engine/A.cpp": '#include "UI/MainWindow.hpp"\n'}), {"module-direction"})

    def test_stems_may_use_ai_but_ai_may_not_use_stems(self):
        self.assertEqual(rules({"Stems/Demucs/A.cpp": '#include "AI/Backends/Cuda/NvmlGpuProbe.hpp"\n'}), set())
        self.assertEqual(rules({"AI/Runtime/A.cpp": '#include "Stems/Cache/Cache.hpp"\n'}), {"module-direction"})

    def test_the_application_composition_root_may_include_everything(self):
        text = '#include "Audio/Engine/AudioEngine.hpp"\n#include "UI/MainWindow.hpp"\n#include "AI/A.hpp"\n'
        self.assertEqual(rules({"Application/Main.cpp": text}), set())

    def test_system_and_unknown_includes_are_ignored(self):
        self.assertEqual(rules({"Core/A.cpp": '#include <vector>\n#include "local_header.hpp"\n'}), set())


class JuceUsageTests(unittest.TestCase):
    def test_core_and_ai_are_juce_free(self):
        self.assertEqual(rules({"Core/A.hpp": "#include <juce_core/juce_core.h>\n"}), {"juce-boundary"})
        self.assertEqual(rules({"AI/A.cpp": "#include <juce_audio_basics/juce_audio_basics.h>\n"}), {"juce-boundary"})

    def test_gui_modules_only_in_ui_and_application(self):
        self.assertEqual(rules({"UI/A.cpp": "#include <juce_gui_basics/juce_gui_basics.h>\n"}), set())
        self.assertEqual(rules({"Application/Main.cpp": "#include <juce_gui_basics/juce_gui_basics.h>\n"}), set())
        self.assertEqual(rules({"Audio/Engine/A.cpp": "#include <juce_gui_basics/juce_gui_basics.h>\n"}), {"juce-boundary"})
        self.assertEqual(rules({"MIDI/A.cpp": "#include <juce_graphics/juce_graphics.h>\n"}), {"juce-boundary"})

    def test_audio_devices_only_in_audio_and_midi(self):
        self.assertEqual(rules({"Audio/Routing/A.cpp": "#include <juce_audio_devices/juce_audio_devices.h>\n"}), set())
        self.assertEqual(rules({"MIDI/A.cpp": "#include <juce_audio_devices/juce_audio_devices.h>\n"}), set())
        self.assertEqual(rules({"UI/A.cpp": "#include <juce_audio_devices/juce_audio_devices.h>\n"}), {"juce-boundary"})

    def test_audio_dsp_is_juce_free_so_it_can_be_tested_without_a_device(self):
        self.assertEqual(rules({"Audio/DSP/A.cpp": "#include <juce_audio_basics/juce_audio_basics.h>\n"}), {"juce-boundary"})
        self.assertEqual(rules({"Audio/Engine/A.cpp": "#include <juce_audio_basics/juce_audio_basics.h>\n"}), set())

    def test_platform_may_use_juce_core(self):
        self.assertEqual(rules({"Platform/Common/A.cpp": "#include <juce_core/juce_core.h>\n"}), set())


class PlatformIsolationTests(unittest.TestCase):
    def test_os_headers_only_in_platform(self):
        self.assertEqual(rules({"Platform/Windows/A.cpp": "#include <windows.h>\n"}), set())
        self.assertEqual(rules({"Audio/Engine/A.cpp": "#include <windows.h>\n"}), {"os-header"})
        self.assertEqual(rules({"Core/A.cpp": "#include <dlfcn.h>\n"}), {"os-header"})
        self.assertEqual(rules({"AI/A.cpp": "#include <cuda_runtime.h>\n"}), {"os-header"})

    def test_platform_conditionals_only_in_platform(self):
        self.assertEqual(rules({"Platform/Posix/A.cpp": "#ifdef __linux__\n#endif\n"}), set())
        self.assertEqual(rules({"Core/A.cpp": "#ifdef _WIN32\n#endif\n"}), {"platform-conditional"})
        self.assertEqual(rules({"UI/A.cpp": "#if defined(__APPLE__)\n#endif\n"}), {"platform-conditional"})
        self.assertEqual(rules({"Audio/A.cpp": "#if _MSC_VER >= 1930\n#endif\n"}), {"platform-conditional"})

    def test_mentions_in_comments_and_strings_do_not_count(self):
        text = '// #ifdef _WIN32 is forbidden here\nconst char* s = "#include <windows.h>";\n'
        self.assertEqual(rules({"Core/A.cpp": text}), set())


class ReportingTests(unittest.TestCase):
    def test_violation_carries_file_line_and_rule(self):
        found = violations_for({"UI/Panel.cpp": '// header\n#include "Audio/Engine/AudioEngine.hpp"\n'})
        self.assertEqual(len(found), 1)
        self.assertEqual((found[0].path, found[0].line, found[0].rule), ("src/UI/Panel.cpp", 2, "module-direction"))

    def test_a_line_can_be_exempted_with_a_reason(self):
        text = '#include "Audio/Engine/AudioEngine.hpp"  // include-lint: allow (temporary, tracked in P2-05)\n'
        self.assertEqual(rules({"UI/Panel.cpp": text}), set())

    def test_an_exemption_without_a_reason_is_refused(self):
        text = '#include "Audio/Engine/AudioEngine.hpp"  // include-lint: allow\n'
        self.assertEqual(rules({"UI/Panel.cpp": text}), {"module-direction"})

    def test_only_source_files_are_scanned(self):
        self.assertEqual(rules({"Core/notes.md": '#include "Audio/X.hpp"\n', "Core/data.json": "#ifdef _WIN32"}), set())

    def test_main_returns_nonzero_on_violations_and_zero_when_clean(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "src" / "Core").mkdir(parents=True)
            (root / "src" / "Core" / "A.cpp").write_text("int x;\n", encoding="utf-8")
            self.assertEqual(check_includes.main(root), 0)
            (root / "src" / "Core" / "B.cpp").write_text("#include <windows.h>\n", encoding="utf-8")
            self.assertEqual(check_includes.main(root), 1)

    def test_main_returns_two_when_there_is_no_src_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(check_includes.main(Path(tmp)), 2)


class RepositoryTests(unittest.TestCase):
    def test_the_real_repository_follows_its_own_architecture(self):
        found = check_includes.check(REPO)
        self.assertEqual([f"{v.path}:{v.line} {v.rule}: {v.message}" for v in found], [])


if __name__ == "__main__":
    unittest.main()
