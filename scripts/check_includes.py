#!/usr/bin/env python3
"""Enforce ZYRON's module boundaries on the source tree (CLAUDE.md layout, ADR-0003, SPEC sections 37 and 78).

Rules (applied to every C++ file under src/):
  module-direction     a module may only include itself, the modules it depends on, and (Application) everything.
  juce-boundary        JUCE usage per module: Core/AI/Stems and Audio/DSP are JUCE-free; GUI modules only in UI and
                       Application; audio devices only in Audio (not DSP) and MIDI.
  os-header            OS / driver headers (windows.h, dlfcn.h, sys/*, CoreAudio, cuda*, nvml.h...) only in Platform
                       (CUDA/NVML headers also in AI/Backends).
  platform-conditional #if/#ifdef on the platform or compiler (_WIN32, __APPLE__, __linux__, _MSC_VER...) only in Platform.

A line can be exempted with a trailing comment that carries a reason:
    #include "Audio/Foo.hpp"  // include-lint: allow (temporary, tracked in P2-05)
An exemption without a reason in parentheses is ignored.

Exit codes: 0 clean, 1 violations found, 2 no src/ directory. Output is plain ASCII.
"""
from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

SOURCE_SUFFIXES = frozenset({".cpp", ".cc", ".cxx", ".hpp", ".h", ".inl"})

# Allowed project-module dependencies (the arrows in CLAUDE.md). A module may always include itself.
MODULE_DEPENDENCIES: dict[str, frozenset[str]] = {
    "Core": frozenset(),
    "Platform": frozenset({"Core"}),
    "AI": frozenset({"Core"}),
    "Audio": frozenset({"Core"}),
    "MIDI": frozenset({"Core"}),
    "Library": frozenset({"Core"}),
    "Analysis": frozenset({"Core"}),
    "Recording": frozenset({"Core"}),
    "Stems": frozenset({"Core", "AI"}),
    "UI": frozenset({"Core"}),
}
COMPOSITION_ROOT = "Application"

JUCE_FREE_MODULES = frozenset({"Core", "AI", "Stems"})
GUI_JUCE = re.compile(r"juce_(gui_\w+|graphics|opengl|audio_utils)$")
DEVICE_JUCE = "juce_audio_devices"
# Where the JUCE-free parts of otherwise JUCE-using modules live (testable without a device).
JUCE_FREE_SUBDIRECTORIES = {"Audio": "DSP"}

OS_HEADER = re.compile(
    r"^(windows\.h|winsock2\.h|dlfcn\.h|unistd\.h|pthread\.h|sys/.+|mach/.+|CoreAudio/.+|CoreFoundation/.+|"
    r"AudioToolbox/.+|cuda\w*\.h|cudnn\w*\.h|nvml\.h)$"
)
GPU_HEADER = re.compile(r"^(cuda\w*\.h|cudnn\w*\.h|nvml\.h)$")
PLATFORM_TOKENS = re.compile(
    r"\b(_WIN32|_WIN64|__APPLE__|__MACH__|__linux__|__unix__|__ANDROID__|_MSC_VER|__GNUC__|__clang__)\b"
)

PROJECT_INCLUDE = re.compile(r'^\s*#\s*include\s+"([A-Za-z0-9_]+)/')
ANGLE_INCLUDE = re.compile(r"^\s*#\s*include\s+<([^>]+)>")
CONDITIONAL = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b(.*)")
EXEMPTION = re.compile(r"include-lint:\s*allow\s*\(\s*\S[^)]*\)")


@dataclass(frozen=True)
class Violation:
    path: str  # relative to the repository root, forward slashes
    line: int
    rule: str
    message: str


def _code_lines(text: str) -> list[tuple[int, str, str]]:
    """(line number, raw line, line with comments removed) for every line. Block comments span lines."""
    result: list[tuple[int, str, str]] = []
    in_block = False
    for number, raw in enumerate(text.splitlines(), start=1):
        out: list[str] = []
        i = 0
        in_string = False
        while i < len(raw):
            two = raw[i:i + 2]
            if in_block:
                if two == "*/":
                    in_block = False
                    i += 2
                else:
                    i += 1
                continue
            ch = raw[i]
            if in_string:
                out.append(ch)
                if ch == "\\" and i + 1 < len(raw):
                    out.append(raw[i + 1])
                    i += 2
                    continue
                if ch == '"':
                    in_string = False
                i += 1
                continue
            if two == "//":
                break
            if two == "/*":
                in_block = True
                i += 2
                continue
            if ch == '"':
                in_string = True
            out.append(ch)
            i += 1
        result.append((number, raw, "".join(out)))
    return result


def _check_file(root: Path, path: Path) -> list[Violation]:
    relative = path.relative_to(root / "src")
    module = relative.parts[0]
    subdirectory = relative.parts[1] if len(relative.parts) > 2 else ""
    display = ("src/" + relative.as_posix())
    found: list[Violation] = []

    def report(line: int, rule: str, message: str) -> None:
        found.append(Violation(display, line, rule, message))

    for number, raw, code in _code_lines(path.read_text(encoding="utf-8", errors="replace")):
        if EXEMPTION.search(raw):
            continue
        stripped = code.lstrip()
        if not stripped.startswith("#"):
            continue

        project = PROJECT_INCLUDE.match(code)
        if project and module != COMPOSITION_ROOT:
            target = project.group(1)
            if target in MODULE_DEPENDENCIES and target != module and target not in MODULE_DEPENDENCIES.get(module, frozenset()):
                report(number, "module-direction", f"{module} must not include {target} ({stripped.strip()})")

        angle = ANGLE_INCLUDE.match(code)
        if angle:
            header = angle.group(1)
            if header.startswith("juce_"):
                juce_module = header.split("/")[0]
                if module in JUCE_FREE_MODULES:
                    report(number, "juce-boundary", f"{module} must stay JUCE-free ({juce_module})")
                elif JUCE_FREE_SUBDIRECTORIES.get(module) == subdirectory:
                    report(number, "juce-boundary", f"{module}/{subdirectory} must stay JUCE-free ({juce_module})")
                elif GUI_JUCE.match(juce_module) and module not in {"UI", COMPOSITION_ROOT}:
                    report(number, "juce-boundary", f"{juce_module} (GUI) is only for UI and Application, not {module}")
                elif juce_module == DEVICE_JUCE and module not in {"Audio", "MIDI", COMPOSITION_ROOT}:
                    report(number, "juce-boundary", f"{juce_module} is only for Audio and MIDI, not {module}")
            elif OS_HEADER.match(header) and module != "Platform":
                gpu_in_backend = GPU_HEADER.match(header) and module == "AI" and subdirectory == "Backends"
                if not gpu_in_backend:
                    report(number, "os-header", f"<{header}> belongs in Platform (or AI/Backends for GPU headers)")

        conditional = CONDITIONAL.match(code)
        if conditional and module != "Platform" and PLATFORM_TOKENS.search(conditional.group(2)):
            report(number, "platform-conditional",
                   "platform/compiler #if outside Platform: put the OS-specific code behind an interface in src/Platform")
    return found


def check(root: Path) -> list[Violation]:
    """All violations under root/src, sorted by file and line."""
    violations: list[Violation] = []
    for path in sorted((root / "src").rglob("*")):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and len(path.relative_to(root / "src").parts) > 1:
            violations.extend(_check_file(root, path))
    return sorted(violations, key=lambda v: (v.path, v.line, v.rule))


def main(root: Path) -> int:
    if not (root / "src").is_dir():
        print(f"check_includes: no src/ directory under {root}", file=sys.stderr)
        return 2
    violations = check(root)
    for v in violations:
        print(f"{v.path}:{v.line}: [{v.rule}] {v.message}")
    if violations:
        print(f"check_includes: {len(violations)} violation(s). Fix them, or exempt a line with "
              "'// include-lint: allow (reason)'.")
        return 1
    print("check_includes: module boundaries OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(__file__).resolve().parent.parent))
